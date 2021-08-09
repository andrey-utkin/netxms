/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2020 Victor Kirhenshtein
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
package org.netxms.nxmc.modules.objects.propertypages;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.netxms.client.NXCObjectModificationData;
import org.netxms.client.NXCSession;
import org.netxms.client.objects.AbstractObject;
import org.netxms.client.objects.BusinessService;
import org.netxms.client.objects.Cluster;
import org.netxms.client.objects.Container;
import org.netxms.client.objects.GenericObject;
import org.netxms.client.objects.interfaces.AutoBindObject;
import org.netxms.nxmc.Registry;
import org.netxms.nxmc.base.jobs.Job;
import org.netxms.nxmc.localization.LocalizationHelper;
import org.netxms.nxmc.modules.nxsl.widgets.ScriptEditor;
import org.netxms.nxmc.tools.WidgetHelper;
import org.xnap.commons.i18n.I18n;

/**
 * "Auto Bind" property page
 */
public class AutoBind extends ObjectPropertyPage
{
   private static I18n i18n = LocalizationHelper.getI18n(AutoBind.class);
   
   private AutoBindObject autoBindObject;
	private Button checkboxEnableBind;
	private Button checkboxEnableUnbind;
	private ScriptEditor filterSource;
   private boolean initialBind;
   private boolean initialUnbind;
	private String initialAutoBindFilter;
	
   public AutoBind(AbstractObject object)
   {
      super(i18n.tr("Auto Bind"), object);
   }

   /**
    * @see org.eclipse.jface.preference.PreferencePage#createContents(org.eclipse.swt.widgets.Composite)
    */
	@Override
	protected Control createContents(Composite parent)
	{
      Composite dialogArea = new Composite(parent, SWT.NONE);
		
		autoBindObject = (AutoBindObject)object;
		if (autoBindObject == null)	// Paranoid check
			return dialogArea;

      initialBind = autoBindObject.isAutoBindEnabled();
      initialUnbind = autoBindObject.isAutoUnbindEnabled();
		initialAutoBindFilter = autoBindObject.getAutoBindFilter();
		
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = WidgetHelper.OUTER_SPACING;
		layout.marginWidth = 0;
		layout.marginHeight = 0;
      dialogArea.setLayout(layout);

      // Enable/disable check box
      checkboxEnableBind = new Button(dialogArea, SWT.CHECK);
      if (autoBindObject instanceof Cluster)
         checkboxEnableBind.setText("Automatically add nodes selected by filter to this cluster");
      else if (autoBindObject instanceof Container)
         checkboxEnableBind.setText(i18n.tr("Automatically bind objects selected by filter to this container"));
      checkboxEnableBind.setSelection(autoBindObject.isAutoBindEnabled());
      checkboxEnableBind.addSelectionListener(new SelectionListener() {
			@Override
			public void widgetDefaultSelected(SelectionEvent e)
			{
				widgetSelected(e);
			}

			@Override
			public void widgetSelected(SelectionEvent e)
			{
				if (checkboxEnableBind.getSelection())
				{
					filterSource.setEnabled(true);
					filterSource.setFocus();
					checkboxEnableUnbind.setEnabled(true);
				}
				else
				{
					filterSource.setEnabled(false);
					checkboxEnableUnbind.setEnabled(false);
				}
			}
      });
      
      checkboxEnableUnbind = new Button(dialogArea, SWT.CHECK);
      if (autoBindObject instanceof Cluster)
         checkboxEnableUnbind.setText("Automatically remove nodes from this cluster when they no longer passes filter");
      else if (autoBindObject instanceof Container)
         checkboxEnableUnbind.setText(i18n.tr("Automatically unbind objects from this container when they no longer passes filter"));
      checkboxEnableUnbind.setSelection(autoBindObject.isAutoUnbindEnabled());
      checkboxEnableUnbind.setEnabled(autoBindObject.isAutoBindEnabled());

      // Filtering script
      Label label = new Label(dialogArea, SWT.NONE);
      label.setText(i18n.tr("Filtering script"));

      GridData gd = new GridData();
      gd.verticalIndent = WidgetHelper.DIALOG_SPACING;
      label.setLayoutData(gd);

      filterSource = new ScriptEditor(dialogArea, SWT.BORDER, SWT.H_SCROLL | SWT.V_SCROLL, true,
            "Variables:\r\n\t$node\tnode being tested (null if object is not a node).\r\n\t$object\tobject being tested.\r\n\t$container\tthis container object.\r\n\r\nReturn value: true to bind node to this container, false to unbind, null to make no changes.");
      filterSource.setText(autoBindObject.getAutoBindFilter());
      filterSource.setEnabled(autoBindObject.isAutoBindEnabled());

		gd = new GridData();
		gd.grabExcessHorizontalSpace = true;
		gd.grabExcessVerticalSpace = true;
		gd.horizontalAlignment = SWT.FILL;
		gd.verticalAlignment = SWT.FILL;
		gd.widthHint = 0;
      gd.heightHint = 0;
		filterSource.setLayoutData(gd);

		return dialogArea;
	}

	/**
	 * Apply changes
	 * 
	 * @param isApply true if update operation caused by "Apply" button
	 */
	protected boolean applyChanges(final boolean isApply)
	{
      boolean apply = checkboxEnableBind.getSelection();
      boolean remove = checkboxEnableUnbind.getSelection();
			
		if ((apply == initialBind) && (remove == initialUnbind) && initialAutoBindFilter.equals(filterSource.getText()))
			return true;		// Nothing to apply

		if (isApply)
			setValid(false);
		
		final NXCSession session =  Registry.getSession();
		final NXCObjectModificationData md = new NXCObjectModificationData(((GenericObject)autoBindObject).getObjectId());
		md.setAutoBindFilter(filterSource.getText());
      int flags = autoBindObject.getAutoBindFlags();
      flags = apply ? flags | AutoBindObject.OBJECT_BIND_FLAG : flags & ~AutoBindObject.OBJECT_BIND_FLAG;  
      flags = remove ? flags | AutoBindObject.OBJECT_UNBIND_FLAG : flags & ~AutoBindObject.OBJECT_UNBIND_FLAG;  
      md.setAutoBindFlags(flags);
		
		new Job(i18n.tr("Update auto-bind filter"), null, null) {
			@Override
			protected void run(IProgressMonitor monitor) throws Exception
			{
				session.modifyObject(md);
		      initialBind = apply;
		      initialUnbind = remove;
				initialAutoBindFilter = md.getAutoBindFilter();
			}

			@Override
			protected void jobFinalize()
			{
				if (isApply)
				{
					runInUIThread(new Runnable() {
						@Override
						public void run()
						{
							AutoBind.this.setValid(true);
						}
					});
				}
			}

			@Override
			protected String getErrorMessage()
			{
				return i18n.tr("Cannot change container automatic bind options");
			}
		}.start();
		
		return true;
	}

   @Override
   public String getId()
   {
      return "autoBind";
   }

   @Override
   public boolean isVisible()
   {
      return (object instanceof AutoBindObject) && !(object instanceof BusinessService);
   }
}
