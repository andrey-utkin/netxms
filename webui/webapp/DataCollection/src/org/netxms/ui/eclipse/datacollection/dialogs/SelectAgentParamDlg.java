/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2010 Victor Kirhenshtein
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
package org.netxms.ui.eclipse.datacollection.dialogs;

import java.util.List;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.dialogs.InputDialog;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Shell;
import org.netxms.client.AgentParameter;
import org.netxms.client.NXCSession;
import org.netxms.client.datacollection.DataCollectionItem;
import org.netxms.ui.eclipse.datacollection.Activator;
import org.netxms.ui.eclipse.jobs.ConsoleJob;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;

/**
 * Dialog for selecting parameters provided by NetXMS agent
 */
public class SelectAgentParamDlg extends AbstractSelectParamDlg
{
	private static final long serialVersionUID = 1L;

	private Button queryButton;
	private Action actionQuery;
	
	/**
	 * @param parentShell
	 * @param nodeId
	 */
	public SelectAgentParamDlg(Shell parentShell, long nodeId)
	{
		super(parentShell, nodeId);
		
		actionQuery = new Action("&Query...") {
			private static final long serialVersionUID = 1L;

			@Override
			public void run()
			{
				querySelectedParameter();
			}
		};
	}

	/* (non-Javadoc)
	 * @see org.eclipse.jface.dialogs.Dialog#createButtonsForButtonBar(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected void createButtonsForButtonBar(Composite parent)
	{
		((GridLayout)parent.getLayout()).numColumns++;
		
		queryButton = new Button(parent, SWT.PUSH);
		queryButton.setText("&Query...");
		GridData gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		queryButton.setLayoutData(gd);
		queryButton.addSelectionListener(new SelectionListener() {
			private static final long serialVersionUID = 1L;

			@Override
			public void widgetSelected(SelectionEvent e)
			{
				querySelectedParameter();
			}
			
			@Override
			public void widgetDefaultSelected(SelectionEvent e)
			{
				widgetSelected(e);
			}
		});
		
		super.createButtonsForButtonBar(parent);
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.datacollection.dialogs.AbstractSelectParamDlg#fillContextMenu(org.eclipse.jface.action.IMenuManager)
	 */
	@Override
	protected void fillContextMenu(IMenuManager manager)
	{
		super.fillContextMenu(manager);
		manager.add(actionQuery);
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.datacollection.dialogs.AbstractSelectParamDlg#fillParameterList()
	 */
	@Override
	protected void fillParameterList()
	{
		final NXCSession session = (NXCSession)ConsoleSharedData.getSession();
		new ConsoleJob("Get list of supported parameters for " + object.getObjectName(), null, Activator.PLUGIN_ID, null) {
			@Override
			protected String getErrorMessage()
			{
				return "Unable to retrieve list of supported parameters";
			}

			@Override
			protected void runInternal(IProgressMonitor monitor) throws Exception
			{
				final List<AgentParameter> parameters = session.getSupportedParameters(object.getObjectId());
				runInUIThread(new Runnable() {
					@Override
					public void run()
					{
						viewer.setInput(parameters.toArray());
					}
				});
			}
		}.start();
	}

	/**
	 * Query current value of selected parameter
	 */
	protected void querySelectedParameter()
	{
		IStructuredSelection selection = (IStructuredSelection)viewer.getSelection();
		if (selection.size() != 1)
			return;
		
		AgentParameter p = (AgentParameter)selection.getFirstElement();
		String n;
		if (p.getName().contains("(*)"))
		{
			InputDialog dlg = new InputDialog(getShell(), "Instance", "Instance for parameter", "", null);
			if (dlg.open() != Window.OK)
				return;
			
			n = p.getName().replace("(*)", "(" + dlg.getValue() + ")");
		}
		else
		{
			n = p.getName();
		}
		
		final NXCSession session = (NXCSession)ConsoleSharedData.getSession();
		final String name = n;
		new ConsoleJob("Query agent", null, Activator.PLUGIN_ID, null) {
			@Override
			protected void runInternal(IProgressMonitor monitor) throws Exception
			{
				final String value = session.queryParameter(object.getObjectId(), DataCollectionItem.AGENT, name);
				runInUIThread(new Runnable() {
					@Override
					public void run()
					{
						MessageDialog.openInformation(getShell(), "Current value", "Current value is \"" + value + "\"");
					}
				});
			}
			
			@Override
			protected String getErrorMessage()
			{
				return "Cannot get current parameter value";
			}
		}.start();
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.datacollection.dialogs.AbstractSelectParamDlg#getConfigurationPrefix()
	 */
	@Override
	protected String getConfigurationPrefix()
	{
		return "SelectAgentParamDlg";
	}
}
