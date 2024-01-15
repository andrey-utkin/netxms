/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2024 Raden Solutions
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
package org.netxms.nxmc.modules.networkmaps.propertypages;

import org.eclipse.swt.SWT;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.netxms.nxmc.base.propertypages.PropertyPage;
import org.netxms.nxmc.localization.LocalizationHelper;
import org.netxms.nxmc.modules.networkmaps.views.helpers.LinkEditor;
import org.netxms.nxmc.resources.ResourceManager;
import org.netxms.nxmc.resources.ThemeEngine;
import org.xnap.commons.i18n.I18n;

/**
 * Base class for link property pages
 */
public abstract class LinkPropertyPage extends PropertyPage
{
   private final I18n i18n = LocalizationHelper.getI18n(LinkPropertyPage.class);

   protected LinkEditor linkEditor;

   /**
    * @param title
    */
   public LinkPropertyPage(LinkEditor linkEditor, String title)
   {
      super(title);
      this.linkEditor = linkEditor;
      noDefaultAndApplyButton();
   }

   /**
    * @see org.eclipse.jface.preference.PreferencePage#createContents(org.eclipse.swt.widgets.Composite)
    */
   @Override
   protected Control createContents(Composite parent)
   {
      Composite dialogArea = new Composite(parent, SWT.NONE);
      GridLayout layout = new GridLayout();
      layout.marginHeight = 0;
      layout.marginWidth = 0;
      setupLayout(layout);
      dialogArea.setLayout(layout);

      if (!linkEditor.isLinkTextUpdateDisabled() && linkEditor.getLink().isAutoGenerated())
      {
         Composite messageArea = new Composite(dialogArea, SWT.BORDER);
         messageArea.setBackground(ThemeEngine.getBackgroundColor("MessageBar"));
         messageArea.setLayout(new GridLayout(2, false));

         GridData gd = new GridData();
         gd.horizontalAlignment = SWT.FILL;
         gd.verticalAlignment = SWT.TOP;
         gd.grabExcessHorizontalSpace = true;
         gd.horizontalSpan = layout.numColumns;
         messageArea.setLayoutData(gd);

         Label imageLabel = new Label(messageArea, SWT.NONE);
         imageLabel.setBackground(messageArea.getBackground());
         imageLabel.setImage(ResourceManager.getImageDescriptor("icons/warning.png").createImage());
         gd = new GridData();
         gd.horizontalAlignment = SWT.LEFT;
         gd.verticalAlignment = SWT.FILL;
         imageLabel.setLayoutData(gd);
         imageLabel.addDisposeListener(new DisposeListener() {
            @Override
            public void widgetDisposed(DisposeEvent e)
            {
               imageLabel.getImage().dispose();
            }
         });

         Label messageLabel = new Label(messageArea, SWT.WRAP);
         messageLabel.setBackground(messageArea.getBackground());
         messageLabel.setForeground(ThemeEngine.getForegroundColor("MessageBar"));
         messageLabel.setText(i18n.tr("This link was created automatically and its labels can be updated by the server at any time"));
         gd = new GridData();
         gd.horizontalAlignment = SWT.FILL;
         gd.verticalAlignment = SWT.CENTER;
         messageLabel.setLayoutData(gd);
      }

      return dialogArea;
   }

   /**
    * Do additional changes on client artea layout. Subclasses may override to alter default layout.
    *
    * @param layout client area layout
    */
   protected void setupLayout(GridLayout layout)
   {
   }
}
