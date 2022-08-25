/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2015 Victor Kirhenshtein
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
package org.netxms.nxmc.modules.dashboards.widgets;

import org.eclipse.swt.SWT;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.netxms.client.dashboards.DashboardElement;
import org.netxms.nxmc.modules.dashboards.config.TableValueConfig;
import org.netxms.nxmc.modules.dashboards.views.AbstractDashboardView;
import org.netxms.nxmc.modules.datacollection.widgets.TableValueViewer;
import org.netxms.nxmc.tools.ViewRefreshController;

/**
 * "Table value" element for dashboard
 */
public class TableValueElement extends ElementWidget
{
	private TableValueConfig config;
	private TableValueViewer viewer;

	/**
	 * @param parent
	 * @param element
	 * @param viewPart
	 */
   public TableValueElement(DashboardControl parent, DashboardElement element, AbstractDashboardView view)
	{
      super(parent, element, view);

		try
		{
			config = TableValueConfig.createFromXml(element.getData());
		}
		catch(Exception e)
		{
			e.printStackTrace();
			config = new TableValueConfig();
		}

      processCommonSettings(config);

      viewer = new TableValueViewer(getContentArea(), SWT.NONE, view, parent.getDashboardObject().getGuid().toString(), true);
		viewer.setObject(config.getObjectId(), config.getDciId());	
		viewer.refresh(null);

      final ViewRefreshController refreshController = new ViewRefreshController(view, config.getRefreshRate(), new Runnable() {
			@Override
			public void run()
			{
				if (TableValueElement.this.isDisposed())
					return;
				
				viewer.refresh(null);
			}
		});

		addDisposeListener(new DisposeListener() {
         @Override
         public void widgetDisposed(DisposeEvent e)
         {
            refreshController.dispose();
         }
      });
	}
}
